/***************************************************************************
  tests/test_lab_batch_v2.cpp — batch classroom v2 (teaching-lab-platform-11):
  identity (sha256 duplicate flags), roster cross-check, submission caps,
  cancellation, and deterministic JSON/HTML summaries.

  Seam under test: sicnu::cli::LabBatchRunner::run() with an injected grade
  callable (same as D7's test_lab_batch — the real grader is D4's). The
  independent oracle for identity is the test's own fixed content bytes: two
  files with equal bytes MUST flag, files with distinct bytes MUST NOT.
 ***************************************************************************/

#include "cli/lab_batch_runner.h"

#include "agent/output_verifier.h"
#include "exprs/exit_codes.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using sicnu::agent::OutputVerifier;
using sicnu::cli::LabBatchOptions;
using sicnu::cli::LabBatchRunner;
using sicnu::cli::LabBatchSummary;
using sicnu::cli::LabBatchRow;

namespace
{

OutputVerifier::LabGradeResult passAt( double score )
{
    OutputVerifier::LabGradeResult r;
    r.graded = true;
    r.verdict = QLatin1String( "pass" );
    r.score = score;
    return r;
}

void writeArtifact( const QString &dir, const QString &name, const QByteArray &bytes )
{
    QFile f( QDir( dir ).filePath( name ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
}

QString makeSubmissions(
  const QTemporaryDir &dir,
  const std::vector<std::pair<QString, QByteArray>> &files )
{
    const QString path = dir.filePath( "submissions" );
    REQUIRE( QDir().mkpath( path ) );
    for ( const auto &file : files )
        writeArtifact( path, file.first, file.second );
    return path;
}

const LabBatchRow *findRow( const LabBatchSummary &summary, const QString &studentId )
{
    for ( const auto &row : summary.rows )
        if ( row.studentId == studentId )
            return &row;
    return nullptr;
}

} // namespace

TEST_CASE( "batch v2: identical content is flagged duplicate_of the first submitter",
           "[lab_batch_v2][identity]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeSubmissions( dir, {
        { "alice.tif", QByteArray( "original work" ) },
        { "bob.tif", QByteArray( "original work 2" ) },
        { "eve.tif", QByteArray( "original work" ) },
    } );

    const auto summary = LabBatchRunner::run(
      path, "ndvi_basics", dir.filePath( "grades.csv" ),
      []( const QString & ) { return passAt( 100.0 ); } );

    REQUIRE( summary.total == 3 );
    REQUIRE( summary.graded == 3 );
    REQUIRE( summary.duplicates == 1 );
    const LabBatchRow *eve = findRow( summary, QStringLiteral( "eve" ) );
    const LabBatchRow *alice = findRow( summary, QStringLiteral( "alice" ) );
    REQUIRE( eve != nullptr );
    REQUIRE( alice != nullptr );
    REQUIRE( eve->duplicateOf == alice->studentId );
    REQUIRE( alice->duplicateOf.isEmpty() );
    // Both rows keep their own grade — no score is invented or dropped.
    REQUIRE( eve->verdict == QLatin1String( "pass" ) );
    REQUIRE( eve->sha256 == alice->sha256 );
    REQUIRE( !eve->sha256.isEmpty() );
}

TEST_CASE( "batch v2: roster flags unknown submitters and missing students",
           "[lab_batch_v2][roster]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeSubmissions( dir, {
        { "2024001.tif", QByteArray( "a" ) },
        { "intruder.tif", QByteArray( "b" ) },
    } );

    QFile rosterFile( dir.filePath( "roster.csv" ) );
    REQUIRE( rosterFile.open( QIODevice::WriteOnly ) );
    rosterFile.write( "\xEF\xBB\xBF" ); // BOM tolerated
    rosterFile.write( "student_id,display_name\r\n" );
    rosterFile.write( "2024001,\xe5\xbc\xa0\xe4\xb8\x89\r\n" ); // Zhang San
    rosterFile.write( "2024002,\xe6\x9d\x8e\xe5\x9b\x9b\r\n" ); // Li Si
    rosterFile.close();

    LabBatchOptions options;
    options.rosterPath = rosterFile.fileName();
    const auto summary = LabBatchRunner::run(
      path, "ndvi_basics", dir.filePath( "grades.csv" ),
      []( const QString & ) { return passAt( 90.0 ); }, options );

    REQUIRE( summary.total == 2 );
    REQUIRE( summary.unknownRoster == 1 );
    REQUIRE( summary.missingRoster == 1 );
    REQUIRE( summary.missingRosterIds == QStringList{ QStringLiteral( "2024002" ) } );
    for ( const auto &row : summary.rows )
        REQUIRE( row.rosterMatch == ( row.studentId == QLatin1String( "2024001" ) ) );
}

TEST_CASE( "batch v2: roster usage failures are typed", "[lab_batch_v2][roster][negative]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeSubmissions( dir, { { "a.tif", QByteArray( "a" ) } } );
    const auto grade = []( const QString & ) { return passAt( 90.0 ); };

    LabBatchOptions options;
    options.rosterPath = dir.filePath( "absent.csv" );
    auto summary = LabBatchRunner::run( path, "ndvi_basics", dir.filePath( "grades.csv" ),
                                        grade, options );
    REQUIRE( summary.usageError );
    REQUIRE( summary.usageMessage.contains( QLatin1String( "cannot read roster" ) ) );

    QFile bad( dir.filePath( "bad.csv" ) );
    REQUIRE( bad.open( QIODevice::WriteOnly ) );
    bad.write( "only-one-column\n" );
    bad.close();
    options.rosterPath = bad.fileName();
    summary = LabBatchRunner::run( path, "ndvi_basics", dir.filePath( "grades.csv" ), grade,
                                   options );
    REQUIRE( summary.usageError );
    REQUIRE( summary.usageMessage.contains( QLatin1String( "not student_id,display_name" ) ) );

    QFile dup( dir.filePath( "dup.csv" ) );
    REQUIRE( dup.open( QIODevice::WriteOnly ) );
    dup.write( "student_id,display_name\n2024001,x\n2024001,y\n" );
    dup.close();
    options.rosterPath = dup.fileName();
    summary = LabBatchRunner::run( path, "ndvi_basics", dir.filePath( "grades.csv" ), grade,
                                   options );
    REQUIRE( summary.usageError );
    REQUIRE( summary.usageMessage.contains( QLatin1String( "duplicate student_id" ) ) );
}

TEST_CASE( "batch v2: max-submissions caps the run; the CSV stays complete",
           "[lab_batch_v2][caps]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<std::pair<QString, QByteArray>> files;
    for ( int i = 0; i < 10; ++i )
        files.emplace_back( QStringLiteral( "s%1.tif" ).arg( i, 2, 10, QLatin1Char( '0' ) ),
                            QByteArray::number( i ) );
    const QString path = makeSubmissions( dir, files );

    LabBatchOptions options;
    options.maxSubmissions = 4;
    const auto summary = LabBatchRunner::run(
      path, "ndvi_basics", dir.filePath( "grades.csv" ),
      []( const QString & ) { return passAt( 70.0 ); }, options );

    REQUIRE( summary.cappedByMaxSubmissions );
    REQUIRE( static_cast<int>( summary.rows.size() ) == 4 );
    REQUIRE( summary.graded == 4 );
    QFile csv( dir.filePath( "grades.csv" ) );
    REQUIRE( csv.open( QIODevice::ReadOnly ) );
    const QList<QByteArray> lines = csv.readAll().split( '\n' );
    REQUIRE( lines.size() == 6 ); // header + 4 rows + trailing split piece
    REQUIRE( sicnu::cli::batchExitCodeFor( summary )
             == exprs::exitCodeValue( exprs::ExitCode::GenericError ) );
}

TEST_CASE( "batch v2: cancel probe stops between submissions", "[lab_batch_v2][cancel]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    std::vector<std::pair<QString, QByteArray>> files;
    for ( int i = 0; i < 5; ++i )
        files.emplace_back( QStringLiteral( "s%1.tif" ).arg( i, 2, 10, QLatin1Char( '0' ) ),
                            QByteArray::number( i ) );
    const QString path = makeSubmissions( dir, files );

    LabBatchOptions options;
    int gradedCount = 0;
    options.cancelled = [&gradedCount]() { return gradedCount >= 2; };
    const auto summary = LabBatchRunner::run(
      path, "ndvi_basics", dir.filePath( "grades.csv" ),
      [&gradedCount]( const QString & )
      {
          ++gradedCount;
          return passAt( 80.0 );
      },
      options );

    REQUIRE( summary.cancelled );
    REQUIRE( static_cast<int>( summary.rows.size() ) == 2 );
    REQUIRE( summary.graded == 2 );
    REQUIRE( sicnu::cli::batchExitCodeFor( summary )
             == exprs::exitCodeValue( exprs::ExitCode::GenericError ) );
}

TEST_CASE( "batch v2: JSON summary is deterministic and duplicate-aware",
           "[lab_batch_v2][summary]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeSubmissions( dir, {
        { "alice.tif", QByteArray( "work-one" ) },
        { "bob.tif", QByteArray( "work-one" ) },
        { "carol.tif", QByteArray( "work-three" ) },
    } );

    LabBatchOptions options;
    options.jsonPath = dir.filePath( "summary.json" );
    const auto summary = LabBatchRunner::run(
      path, "ndvi_basics", dir.filePath( "grades.csv" ),
      []( const QString & ) { return passAt( 95.0 ); }, options );
    REQUIRE( !summary.usageError );

    QFile summaryFile( options.jsonPath );
    REQUIRE( summaryFile.open( QIODevice::ReadOnly ) );
    const QByteArray firstRun = summaryFile.readAll();
    const QJsonObject body = QJsonDocument::fromJson( firstRun ).object();
    REQUIRE( body.value( QLatin1String( "schema" ) ).toString()
             == QLatin1String( "sicnu.lab.batch-summary/1" ) );
    REQUIRE( body.value( QLatin1String( "duplicates" ) ).toInt() == 1 );
    REQUIRE( body.value( QLatin1String( "total" ) ).toInt() == 3 );
    const auto rows = body.value( QLatin1String( "rows" ) ).toArray();
    REQUIRE( rows.size() == 3 );
    // rows sorted by student_id regardless of discovery order
    REQUIRE( rows.at( 0 ).toObject().value( QLatin1String( "student_id" ) ).toString()
             == QLatin1String( "alice" ) );
    REQUIRE( rows.at( 1 ).toObject().value( QLatin1String( "duplicate_of" ) ).toString()
             == QLatin1String( "alice" ) );
    // every row carries its content identity
    REQUIRE( !rows.at( 0 ).toObject().value( QLatin1String( "sha256" ) ).toString().isEmpty() );

    // Determinism: an identical rerun produces byte-identical JSON.
    LabBatchOptions rerunOptions;
    rerunOptions.jsonPath = dir.filePath( "summary2.json" );
    LabBatchRunner::run( path, "ndvi_basics", dir.filePath( "grades2.csv" ),
                         []( const QString & ) { return passAt( 95.0 ); }, rerunOptions );
    QFile summary2( rerunOptions.jsonPath );
    REQUIRE( summary2.open( QIODevice::ReadOnly ) );
    REQUIRE( summary2.readAll() == firstRun );
}

TEST_CASE( "batch v2: HTML summary renders every row and escapes user text",
           "[lab_batch_v2][summary][html]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // A student id carrying HTML metacharacters must never inject markup
    // (& and quotes are legal in Windows file names; escaped on rendering).
    const QString path = makeSubmissions( dir, {
        { "a&b!.tif", QByteArray( "work" ) },
    } );

    LabBatchOptions options;
    options.htmlPath = dir.filePath( "summary.html" );
    const auto summary = LabBatchRunner::run(
      path, "ndvi_basics", dir.filePath( "grades.csv" ),
      []( const QString & ) { return passAt( 88.0 ); }, options );
    REQUIRE( !summary.usageError );

    QFile html( options.htmlPath );
    REQUIRE( html.open( QIODevice::ReadOnly ) );
    const QString text = QString::fromUtf8( html.readAll() );
    REQUIRE( text.contains( QLatin1String( "a&amp;b!.tif" ) ) );
    REQUIRE( !text.contains( QLatin1String( "<td>a&b!.tif</td>" ) ) );
    REQUIRE( text.contains( QLatin1String( "<td>88.0</td>" ) ) );
}

TEST_CASE( "batch v2: summary targets are excluded from discovery",
           "[lab_batch_v2][contract]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeSubmissions( dir, { { "alice.tif", QByteArray( "work" ) } } );

    LabBatchOptions options;
    // Summary targets INSIDE the submissions dir must not be graded.
    options.jsonPath = QDir( path ).filePath( "summary.json" );
    options.htmlPath = QDir( path ).filePath( "summary.html" );
    const auto summary = LabBatchRunner::run(
      path, "ndvi_basics", QDir( path ).filePath( "grades.csv" ),
      []( const QString & ) { return passAt( 90.0 ); }, options );
    REQUIRE( summary.total == 1 );
    REQUIRE( summary.graded == 1 );
}

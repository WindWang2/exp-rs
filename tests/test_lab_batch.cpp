/***************************************************************************
  tests/test_lab_batch.cpp — D7 batch grading: streamed, error-isolated CSV
  export over the OutputVerifier::gradeArtifact teaching seam (ADR 0146).

  Seam under test: sicnu::cli::LabBatchRunner::run() with an injected grade
  callable. The CSV file is the observable artifact; streaming is observed
  from inside the injected callable (row N-1 must already be flushed when
  submission N is graded). No GDAL — the real grader is D4's, already tested.
 ***************************************************************************/

#include "cli/lab_batch_runner.h"

#include "agent/output_verifier.h"
#include "exprs/exit_codes.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using sicnu::agent::OutputVerifier;
using sicnu::cli::LabBatchRunner;
using sicnu::cli::LabBatchSummary;

namespace
{
OutputVerifier::LabGradeResult graded( const QString &verdict, double score,
                                       std::vector<OutputVerifier::LabDeduction> deductions = {} )
{
    OutputVerifier::LabGradeResult result;
    result.graded = true;
    result.verdict = verdict;
    result.score = score;
    result.deductions = std::move( deductions );
    return result;
}

OutputVerifier::LabDeduction deduction( const QString &assertionId, double weight,
                                        const QString &severity = "normal" )
{
    OutputVerifier::LabDeduction d;
    d.assertionId = assertionId;
    d.weight = weight;
    d.severity = severity;
    d.kind = "range";
    d.message = "deduction evidence for " + assertionId;
    return d;
}

/// Creates a submissions dir with one empty artifact per student id.
QString makeSubmissions( const QTemporaryDir &dir,
                         const std::vector<std::string> &studentIds )
{
    const QString path = dir.filePath( "submissions" );
    QDir().mkpath( path );
    for ( const std::string &id : studentIds )
    {
        QFile f( QDir( path ).filePath( QString::fromStdString( id ) + ".tif" ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "artifact" );
    }
    return path;
}

QString readCsv( const QString &path )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    return QString::fromUtf8( f.readAll() );
}

std::vector<QString> csvLines( const QString &content )
{
    const QString body = content.startsWith( QLatin1String( "\xEF\xBB\xBF" ) )
                           ? content.mid( 3 )
                           : content;
    std::vector<QString> lines;
    for ( const QString &line : body.split( QLatin1Char( '\n' ), Qt::SkipEmptyParts ) )
        lines.push_back( line );
    return lines;
}
} // namespace

TEST_CASE( "batch CSV: BOM, schema header and one row per submission",
           "[lab_batch][d7]" )
{
    QTemporaryDir dir;
    const QString submissions = makeSubmissions( dir, { "20240101", "20240102" } );
    const QString csv = dir.filePath( "grades.csv" );

    const LabBatchSummary summary = LabBatchRunner::run(
      submissions, "ndvi_basics", csv,
      []( const QString & )
      {
          return graded( "pass", 92.5 );
      } );

    REQUIRE( summary.total == 2 );
    REQUIRE( summary.graded == 2 );
    REQUIRE( summary.isolated == 0 );

    const QString content = readCsv( csv );
    // UTF-8 BOM first (Excel-on-Windows contract), then the fixed schema.
    REQUIRE( content.startsWith( QLatin1String( "\xEF\xBB\xBF" ) ) );
    const std::vector<QString> lines = csvLines( content );
    REQUIRE( lines.size() == 3 );
    CHECK( lines[0].toStdString()
           == "student_id,lab_id,score,verdict,top_deduction,artifact_path" );
    // Deterministic discovery order (sorted by filename).
    CHECK( lines[1].toStdString().rfind( "20240101,ndvi_basics,92.5,pass,", 0 ) == 0 );
    CHECK( lines[2].toStdString().rfind( "20240102,ndvi_basics,92.5,pass,", 0 ) == 0 );
}

TEST_CASE( "batch CSV streams: row N-1 is flushed before submission N is graded",
           "[lab_batch][d7]" )
{
    QTemporaryDir dir;
    const QString submissions = makeSubmissions( dir, { "a1", "a2", "a3" } );
    const QString csv = dir.filePath( "grades.csv" );

    int calls = 0;
    const LabBatchSummary summary = LabBatchRunner::run(
      submissions, "ndvi_basics", csv,
      [&]( const QString & )
      {
          ++calls;
          if ( calls == 2 )
          {
              // While grading the SECOND submission the FIRST row must already
              // be on disk — memory stays bounded by one submission.
              const std::vector<QString> lines = csvLines( readCsv( csv ) );
              REQUIRE( lines.size() == 2 ); // header + row 1
              CHECK( lines[1].toStdString().rfind( "a1,", 0 ) == 0 );
          }
          return graded( "fail", 55.0 );
      } );

    REQUIRE( calls == 3 );
    REQUIRE( summary.total == 3 );
    REQUIRE( summary.graded == 3 );
}

TEST_CASE( "batch CSV: highest-weight failed assertion is the top_deduction",
           "[lab_batch][d7]" )
{
    QTemporaryDir dir;
    const QString submissions = makeSubmissions( dir, { "20240101" } );
    const QString csv = dir.filePath( "grades.csv" );

    std::ignore = LabBatchRunner::run(
      submissions, "ndvi_basics", csv,
      []( const QString & )
      {
          return graded( "fail", 55.0, { deduction( "range.ndvi", 10.0 ),
                                         deduction( "grid.compatible", 40.0, "blocking" ) } );
      } );

    const std::vector<QString> lines = csvLines( readCsv( csv ) );
    REQUIRE( lines.size() == 2 );
    CHECK( lines[1].toStdString()
             .find( "grid.compatible" ) != std::string::npos );
}

TEST_CASE( "batch CSV: a throwing submission is isolated, never aborts the run",
           "[lab_batch][d7]" )
{
    QTemporaryDir dir;
    const QString submissions = makeSubmissions( dir, { "s1", "s2", "s3" } );
    const QString csv = dir.filePath( "grades.csv" );

    int calls = 0;
    const LabBatchSummary summary = LabBatchRunner::run(
      submissions, "ndvi_basics", csv,
      [&]( const QString & )
      {
          if ( ++calls == 2 )
              throw std::runtime_error( "corrupt raster, missing band,1 \"red\"" );
          return graded( "pass", 88.0 );
      } );

    REQUIRE( summary.total == 3 );
    REQUIRE( summary.graded == 2 );
    REQUIRE( summary.isolated == 1 );

    const std::vector<QString> lines = csvLines( readCsv( csv ) );
    REQUIRE( lines.size() == 4 );
    CHECK( lines[2].toStdString().rfind( "s2,ndvi_basics,,error,", 0 ) == 0 );
    // The message survived CSV quoting: commas and quotes round-trip.
    CHECK( lines[2].toStdString().find( "\"corrupt raster, missing band,1 \"\"red\"\"\"" )
             != std::string::npos );
    // The run continued after the isolated row.
    CHECK( lines[3].toStdString().rfind( "s3,ndvi_basics,88.0,pass,", 0 ) == 0 );
}

TEST_CASE( "batch CSV: unverifiable submissions keep the error text",
           "[lab_batch][d7]" )
{
    QTemporaryDir dir;
    const QString submissions = makeSubmissions( dir, { "20240101" } );
    const QString csv = dir.filePath( "grades.csv" );

    const LabBatchSummary summary = LabBatchRunner::run(
      submissions, "ndvi_basics", csv,
      []( const QString & )
      {
          OutputVerifier::LabGradeResult result;
          result.graded = false;
          result.verdict = "unverifiable";
          result.errorClass = "artifact";
          result.error = "not a raster GDAL can open";
          return result;
      } );

    CHECK( summary.graded == 0 );
    CHECK( summary.isolated == 0 );

    const std::vector<QString> lines = csvLines( readCsv( csv ) );
    REQUIRE( lines.size() == 2 );
    CHECK( lines[1].toStdString()
             == "20240101,ndvi_basics,,unverifiable,not a raster GDAL can open," );
}

TEST_CASE( "batch discovery: directories, hidden files and the CSV target are skipped",
           "[lab_batch][d7]" )
{
    QTemporaryDir dir;
    const QString submissions = makeSubmissions( dir, { "s1" } );
    QDir( submissions ).mkpath( "nested_dir" );
    QFile( QDir( submissions ).filePath( ".hidden.tif" ) ).open( QIODevice::WriteOnly );
    const QString csv = QDir( submissions ).filePath( "grades.csv" );

    const LabBatchSummary summary = LabBatchRunner::run(
      submissions, "ndvi_basics", csv,
      []( const QString & ) { return graded( "pass", 90.0 ); } );

    CHECK( summary.total == 1 );
}

TEST_CASE( "batch usage: missing submissions directory is a usage failure",
           "[lab_batch][d7]" )
{
    QTemporaryDir dir;
    const LabBatchSummary summary = LabBatchRunner::run(
      dir.filePath( "does-not-exist" ), "ndvi_basics", dir.filePath( "grades.csv" ),
      []( const QString & ) { return graded( "pass", 90.0 ); } );

    CHECK( summary.usageError == true );
    CHECK( summary.total == 0 );
}

TEST_CASE( "batch exit-code contract", "[lab_batch][d7]" )
{
    namespace exprs_ns = exprs;
    // All graded -> 0 even when every verdict is "fail" (grading worked).
    CHECK( sicnu::cli::batchExitCodeFor( { 3, 3, 0, false } )
           == exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok ) );
    // Any isolated row -> nonzero but the CSV is complete.
    CHECK( sicnu::cli::batchExitCodeFor( { 3, 2, 1, false } )
           == exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError ) );
    // Usage failure (missing dir).
    CHECK( sicnu::cli::batchExitCodeFor( { 0, 0, 0, true } )
           == exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ) );
}

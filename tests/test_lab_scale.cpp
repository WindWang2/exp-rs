/***************************************************************************
  tests/test_lab_scale.cpp — classroom scale (teaching-lab-11, package H).

  The daily gate grades 100 synthetic submissions (tiny GeoTIFFs generated in
  a temp dir — memory stays bounded by construction); the 1000-submission run
  is opt-in via SICNU_LAB_SCALE_1000=1 and executed for the track evidence
  record (PERFORMANCE.md), not for the daily gate.

  Logical invariants (never wall-clock):
    * every submission graded, no error rows, scores deterministic — a full
      rerun produces byte-identical CSV and JSON summaries;
    * a controlled duplicate pattern (every 50th submission = student 0's
      bytes) is detected exactly N/50-1 times;
    * a cancel at row K keeps the CSV prefix complete (K rows + header).
 ***************************************************************************/

#include "cli/lab_batch_runner.h"

#include "agent/output_verifier.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <gdal_priv.h>

#include <string>
#include <vector>

using sicnu::agent::OutputVerifier;
using sicnu::cli::LabBatchOptions;
using sicnu::cli::LabBatchRunner;
using sicnu::cli::LabBatchSummary;

namespace {

struct GdalInit
{
    GdalInit() { GDALAllRegister(); }
};
const GdalInit s_gdalInit;

/// A tiny but real single-band float32 GeoTIFF with student-specific
/// closed-form content (no RNG): value = base + 0.001 * (x + y).
QString writeSubmission( const QString &dir, const QString &studentId, double base )
{
    constexpr int kW = 8, kH = 8;
    const QString path = QDir( dir ).filePath( studentId + ".tif" );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDataset *dataset = driver->Create( path.toUtf8().constData(), kW, kH, 1, GDT_Float32,
                                           nullptr );
    REQUIRE( dataset );
    std::vector<float> row( static_cast<size_t>( kW ) );
    GDALRasterBand *band = dataset->GetRasterBand( 1 );
    for ( int y = 0; y < kH; ++y )
    {
        for ( int x = 0; x < kW; ++x )
            row[static_cast<size_t>( x )] = static_cast<float>( base + 0.001 * ( x + y ) );
        REQUIRE( band->RasterIO( GF_Write, 0, y, kW, 1, row.data(), kW, 1, GDT_Float32, 0, 0 )
                 == CE_None );
    }
    GDALClose( dataset );
    return path;
}

/// Real grader over the tiny artifacts (exercises the actual GDAL path).
OutputVerifier::LabGradeResult realGrade( const QString &artifactPath )
{
    static const OutputVerifier verifier;
    return verifier.gradeArtifact( "ndvi_basics", artifactPath, {} );
}

} // namespace

namespace
{

/// Runs the batch over @p count submissions (student ids s0000..), grades
/// them deterministically, and returns the summary. Duplicates: every 50th
/// submission re-writes student 0's file bytes.
LabBatchSummary runScaledBatch( const QTemporaryDir &dir, const QString &submissions,
                                int count, bool withRealGrader, const QString &jsonPath )
{
    for ( int i = 0; i < count; ++i )
    {
        const QString id = QStringLiteral( "s%1" ).arg( i, 4, 10, QLatin1Char( '0' ) );
        if ( i % 50 == 0 )
        {
            // The canonical file: s0000's bytes land again under a new id.
            const QString canonical = QDir( submissions ).filePath( "s0000.tif" );
            if ( QFile::exists( canonical ) )
            {
                QFile::copy( canonical, QDir( submissions ).filePath( id + ".tif" ) );
                continue;
            }
        }
        writeSubmission( submissions, id, 0.4 + 0.0001 * i );
    }

    LabBatchOptions options;
    options.jsonPath = jsonPath;
    if ( withRealGrader )
        return LabBatchRunner::run( submissions, "ndvi_basics",
                                    dir.filePath( "grades.csv" ), realGrade, options );
    return LabBatchRunner::run(
      submissions, "ndvi_basics", dir.filePath( "grades.csv" ),
      []( const QString & )
      {
          OutputVerifier::LabGradeResult r;
          r.graded = true;
          r.verdict = QLatin1String( "pass" );
          r.score = 100.0;
          return r;
      },
      options );
}

} // namespace

TEST_CASE( "scale: 100 synthetic submissions grade deterministically with "
           "duplicate detection",
           "[lab_scale][bounded]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString submissions = dir.filePath( "submissions" );
    REQUIRE( QDir().mkpath( submissions ) );

    const auto summary = runScaledBatch( dir, submissions, 100, false,
                                         dir.filePath( "summary.json" ) );
    REQUIRE( summary.total == 100 );
    REQUIRE( summary.graded == 100 );
    REQUIRE( summary.isolated == 0 );
    // s0050 duplicates s0000's file (canonical copy).
    REQUIRE( summary.duplicates == 1 );

    // Deterministic rerun: byte-identical CSV and JSON.
    const QString csv1 = dir.filePath( "grades.csv" );
    const QString json1 = dir.filePath( "summary.json" );
    QByteArray csvBytes1, jsonBytes1;
    {
        QFile f( csv1 );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        csvBytes1 = f.readAll();
        QFile j( json1 );
        REQUIRE( j.open( QIODevice::ReadOnly ) );
        jsonBytes1 = j.readAll();
    }
    LabBatchOptions rerun;
    rerun.jsonPath = dir.filePath( "summary2.json" );
    const auto rerunSummary = LabBatchRunner::run(
      submissions, "ndvi_basics", dir.filePath( "grades2.csv" ),
      []( const QString & )
      {
          OutputVerifier::LabGradeResult r;
          r.graded = true;
          r.verdict = QLatin1String( "pass" );
          r.score = 100.0;
          return r;
      },
      rerun );
    REQUIRE( rerunSummary.total == 100 );
    QFile csv2( dir.filePath( "grades2.csv" ) );
    REQUIRE( csv2.open( QIODevice::ReadOnly ) );
    REQUIRE( csv2.readAll() == csvBytes1 );
    QFile json2( rerun.jsonPath );
    REQUIRE( json2.open( QIODevice::ReadOnly ) );
    REQUIRE( json2.readAll() == jsonBytes1 );
}

TEST_CASE( "scale: 25 submissions grade through the REAL grader seam",
           "[lab_scale][real_grader]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString submissions = dir.filePath( "submissions" );
    REQUIRE( QDir().mkpath( submissions ) );

    // ndvi_basics rules grade NDVI distribution properties; the tiny
    // synthetic rasters will fail some assertions — that is FINE here: the
    // scale invariant is "graded without error rows", not the verdict.
    for ( int i = 0; i < 25; ++i )
        writeSubmission( submissions, QStringLiteral( "s%1" ).arg( i, 4, 10, QLatin1Char( '0' ) ),
                         0.5 + 0.001 * i );
    LabBatchOptions options;
    const auto summary = LabBatchRunner::run(
      submissions, "ndvi_basics", dir.filePath( "grades.csv" ), realGrade, options );
    REQUIRE( summary.total == 25 );
    REQUIRE( summary.isolated == 0 );
    int gradedOrUnverifiable = 0;
    for ( const auto &row : summary.rows )
        if ( row.verdict == QLatin1String( "pass" ) || row.verdict == QLatin1String( "fail" )
             || row.verdict == QLatin1String( "unverifiable" ) )
            ++gradedOrUnverifiable;
    REQUIRE( gradedOrUnverifiable == 25 );
}

TEST_CASE( "scale: cancel at row K keeps the CSV prefix complete",
           "[lab_scale][cancel]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString submissions = dir.filePath( "submissions" );
    REQUIRE( QDir().mkpath( submissions ) );
    for ( int i = 0; i < 40; ++i )
        writeSubmission( submissions, QStringLiteral( "s%1" ).arg( i, 4, 10, QLatin1Char( '0' ) ),
                         0.5 );

    LabBatchOptions options;
    int graded = 0;
    options.cancelled = [&graded]() { return graded >= 17; };
    const auto summary = LabBatchRunner::run(
      submissions, "ndvi_basics", dir.filePath( "grades.csv" ),
      [&graded]( const QString & )
      {
          ++graded;
          OutputVerifier::LabGradeResult r;
          r.graded = true;
          r.verdict = QLatin1String( "pass" );
          r.score = 90.0;
          return r;
      },
      options );

    REQUIRE( summary.cancelled );
    REQUIRE( summary.rows.size() == 17 );
    QFile csv( dir.filePath( "grades.csv" ) );
    REQUIRE( csv.open( QIODevice::ReadOnly ) );
    const QList<QByteArray> lines = csv.readAll().split( '\n' );
    REQUIRE( lines.size() == 19 ); // header + 17 rows + trailing split piece
    REQUIRE( !lines.at( 17 ).isEmpty() ); // row 17 flushed before cancel check
}

TEST_CASE( "scale: 1000 synthetic submissions (opt-in evidence run)",
           "[lab_scale][.scale1000]" )
{
    // Opt-in per the track resource envelope: the daily gate never pays for
    // this; the evidence run executes it explicitly with the env var set.
    const QByteArray enabled = qgetenv( "SICNU_LAB_SCALE_1000" );
    if ( enabled.isEmpty() )
        SKIP();

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString submissions = dir.filePath( "submissions" );
    REQUIRE( QDir().mkpath( submissions ) );

    const auto summary = runScaledBatch( dir, submissions, 1000, false,
                                         dir.filePath( "summary.json" ) );
    REQUIRE( summary.total == 1000 );
    REQUIRE( summary.graded == 1000 );
    REQUIRE( summary.isolated == 0 );
    REQUIRE( summary.duplicates == 19 ); // s0050, s0100, ... s0950 (not s0000)

    QFile json( dir.filePath( "summary.json" ) );
    REQUIRE( json.open( QIODevice::ReadOnly ) );
    const QJsonObject body = QJsonDocument::fromJson( json.readAll() ).object();
    REQUIRE( body.value( QLatin1String( "total" ) ).toInt() == 1000 );
    REQUIRE( body.value( QLatin1String( "rows" ) ).toArray().size() == 1000 );
}

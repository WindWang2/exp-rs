// tests/test_r23_c_teaching_pair_publish.cpp — r23-C (C10) regression.
//
// #1695-17e: publishBatchOutputsAtomic used to publish the JSON first and
// only then build and write the CSV, so a second-artifact failure (a
// directory squatting on the .csv name, ENOSPC after the JSON landed) left
// the FRESH JSON published beside a STALE CSV — the gradebook pair disagreed
// about the run. The pair must publish together or not at all: a failed
// publish leaves the previous pair intact, and a successful one replaces
// both artifacts.
#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using sicnu::teaching_admin::BatchAssessmentReport;
using sicnu::teaching_admin::BatchRowResult;
using sicnu::teaching_admin::publishBatchOutputsAtomic;

namespace
{

BatchAssessmentReport oneRowReport()
{
    BatchAssessmentReport report;
    report.labId = QStringLiteral( "lab-01" );
    report.total = 1;
    report.graded = 1;
    BatchRowResult row;
    row.studentId = QStringLiteral( "alice" );
    row.labId = report.labId;
    row.status = QStringLiteral( "pass" );
    row.score = 92.5;
    row.verdict = QStringLiteral( "pass" );
    row.message = QStringLiteral( "ok" );
    row.artifactPath = QStringLiteral( "/tmp/alice/lab-01.zip" );
    report.rows.append( row );
    return report;
}

bool writeFile( const QString &path, const QByteArray &bytes )
{
    QFile f( path );
    if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    f.write( bytes );
    f.close();
    return true;
}

QByteArray readFile( const QString &path )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return {};
    return f.readAll();
}

} // namespace

TEST_CASE( "batch publish leaves the previous pair intact when the csv slot cannot be replaced (#1695)",
           "[teaching_admin][r23c]" )
{
    const BatchAssessmentReport report = oneRowReport();
    QTemporaryDir dir;
    const QString prefix = dir.filePath( QStringLiteral( "run/pair" ) );
    REQUIRE( QDir().mkpath( dir.filePath( QStringLiteral( "run" ) ) ) );
    const QString jsonPath = prefix + QStringLiteral( ".json" );
    const QString csvPath = prefix + QStringLiteral( ".csv" );

    // A previous run's pair exists; the CSV slot has since been squatted by
    // a directory (a rename publish can never replace it).
    REQUIRE( writeFile( jsonPath, QByteArrayLiteral( "{\"stale\":true}") ) );
    REQUIRE( writeFile( csvPath, QByteArrayLiteral( "STALE" ) ) );
    REQUIRE( QFile::remove( csvPath ) );
    REQUIRE( QDir().mkdir( csvPath ) );

    REQUIRE_FALSE( publishBatchOutputsAtomic( report, prefix ) );

    // RED behavior: the JSON had already been published when the CSV write
    // failed, so the fresh (mismatched) JSON replaced the stale one and the
    // surviving pair disagreed about the run.
    const QByteArray jsonNow = readFile( jsonPath );
    CAPTURE( jsonNow );
    REQUIRE( jsonNow == QByteArrayLiteral( "{\"stale\":true}") );
}

TEST_CASE( "batch publish refuses a directory squatting the json slot without touching the csv (#1695)",
           "[teaching_admin][r23c]" )
{
    const BatchAssessmentReport report = oneRowReport();
    QTemporaryDir dir;
    const QString prefix = dir.filePath( QStringLiteral( "run/pair" ) );
    REQUIRE( QDir().mkpath( dir.filePath( QStringLiteral( "run" ) ) ) );
    const QString jsonPath = prefix + QStringLiteral( ".json" );
    const QString csvPath = prefix + QStringLiteral( ".csv" );

    REQUIRE( QDir().mkdir( jsonPath ) );
    REQUIRE( writeFile( csvPath, QByteArrayLiteral( "STALE" ) ) );

    REQUIRE_FALSE( publishBatchOutputsAtomic( report, prefix ) );

    // The untouched member keeps the previous run's content.
    REQUIRE( readFile( csvPath ) == QByteArrayLiteral( "STALE" ) );
    // No staging residue beside the pair: the refusal happened before
    // anything was staged, so the directory holds exactly the two slots.
    const QStringList entries = QDir( dir.filePath( QStringLiteral( "run" ) ) )
                                  .entryList( QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot );
    CAPTURE( entries.join( QStringLiteral( ", " ) ) );
    CHECK( entries.size() == 2 );
    CHECK( entries.contains( QStringLiteral( "pair.json" ) ) );
    CHECK( entries.contains( QStringLiteral( "pair.csv" ) ) );
}

TEST_CASE( "batch publish replaces both artifacts together on success (#1695)",
           "[teaching_admin][r23c]" )
{
    const BatchAssessmentReport report = oneRowReport();
    QTemporaryDir dir;
    const QString prefix = dir.filePath( QStringLiteral( "run/pair" ) );
    REQUIRE( QDir().mkpath( dir.filePath( QStringLiteral( "run" ) ) ) );
    const QString jsonPath = prefix + QStringLiteral( ".json" );
    const QString csvPath = prefix + QStringLiteral( ".csv" );

    // Previous pair exists — success must replace BOTH in one shot.
    REQUIRE( writeFile( jsonPath, QByteArrayLiteral( "{\"stale\":true}") ) );
    REQUIRE( writeFile( csvPath, QByteArrayLiteral( "STALE" ) ) );

    REQUIRE( publishBatchOutputsAtomic( report, prefix ) );

    const QJsonObject doc = QJsonDocument::fromJson( readFile( jsonPath ) ).object();
    REQUIRE( doc.value( QStringLiteral( "lab_id" ) ).toString() == QStringLiteral( "lab-01" ) );
    const QByteArray csv = readFile( csvPath );
    REQUIRE( csv.startsWith( QByteArrayLiteral( "\xEF\xBB\xBF" ) ) );
    REQUIRE( csv.contains( QByteArrayLiteral( "\"alice\"" ) ) );
    // The old pair's marker is gone from both artifacts.
    CHECK_FALSE( readFile( jsonPath ).contains( QByteArrayLiteral( "stale" ) ) );
    CHECK_FALSE( csv.contains( QByteArrayLiteral( "STALE" ) ) );
    // No publish residue: backups and journal are cleaned on commit.
    const QStringList entries = QDir( dir.filePath( QStringLiteral( "run" ) ) )
                                  .entryList( QDir::Files | QDir::NoDotAndDotDot );
    CAPTURE( entries.join( QStringLiteral( ", " ) ) );
    CHECK( entries.size() == 2 );
}

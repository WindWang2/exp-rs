/***************************************************************************
  tests/test_lab_report_cli.cpp — headless `lab --report` (teaching-lab-11):
  the CLI shell over the D5 LabReportBuilder authority.

  Oracles (independent of the runner):
    * redaction  — a secret planted in the run's environment must appear in
      NO rendering; the test plants it and greps every output file;
    * recorded grade — the transcript written by `lab --grade --out` is a
      {schema, digest, report} document built HERE with a known digest; the
      exported report must embed gradingRef == that digest verbatim;
    * determinism — identical inputs + fixed generated_at give byte-identical
      files (no wall clock in the body);
    * typed usage failures (unknown experiment → report_no_experiment).
 ***************************************************************************/

#include "cli/lab_report_runner.h"

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "exprs/exit_codes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <string>

using sicnu::cli::LabReportOptions;
using sicnu::experiment::Experiment;
using sicnu::experiment::ExperimentRun;
using sicnu::experiment::ExperimentStore;
using sicnu::experiment::RunEnvironment;
using sicnu::experiment::RunStatus;

namespace
{

constexpr const char *kPlantedToken = "SICNU_LAB_E2E_TOKEN=super-secret-classroom-token";

/// Seeds one experiment with one completed run carrying a planted secret.
QString seedStore( const QTemporaryDir &dir )
{
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "lab-exp-1" ) );
    experiment.setName( QStringLiteral( "NDVI 教学实验" ) );
    experiment.setObjective( QStringLiteral( "理解归一化植被指数" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-lab-1" ) );
    run.setExperimentId( QStringLiteral( "lab-exp-1" ) );
    run.setAlgorithmId( QStringLiteral( "rs:spectral_index" ) );
    run.setAlgorithmVersion( QStringLiteral( "2.1" ) );
    QJsonObject parameters;
    parameters.insert( QStringLiteral( "index" ), QStringLiteral( "ndvi" ) );
    run.setParameters( parameters );
    run.setSeed( 7 );
    run.setSoftwareRevision( QStringLiteral( "rev-cli-test" ) );
    QHash<QString, QString> envVariables;
    envVariables.insert( QStringLiteral( "SICNU_LAB_E2E_TOKEN" ),
                         QStringLiteral( "super-secret-classroom-token" ) );
    run.setEnvironment( RunEnvironment::fromFields( {}, envVariables ) );
    run.setStartedAtUtc( QDateTime::currentDateTimeUtc().addSecs( -60 ) );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc().addSecs( -10 ) );
    REQUIRE( store.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Running );
    REQUIRE( store.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Completed );
    ExperimentRun::Artifact artifact;
    artifact.path = dir.filePath( QStringLiteral( "ndvi.tif" ) );
    artifact.role = QStringLiteral( "step-1" );
    artifact.digest = QStringLiteral( "deadbeef" );
    artifact.sizeBytes = 1024;
    run.artifacts().append( artifact );
    REQUIRE( store.upsertRun( run ).has_value() );
    store.close();
    return dir.filePath( QStringLiteral( "experiments.db" ) );
}

LabReportOptions baseOptions( const QString &db, const QTemporaryDir &outDir )
{
    LabReportOptions options;
    options.experimentDb = db;
    options.experimentId = QStringLiteral( "lab-exp-1" );
    options.student = QStringLiteral( "2024001" );
    options.session = QStringLiteral( "session-1" );
    // Fix the ONE header field allowed to vary — body must be identical.
    options.generatedAtUtc = QStringLiteral( "2026-09-16T08:00:00Z" );
    options.outBase = outDir.filePath( QStringLiteral( "report" ) );
    return options;
}

QString readFile( const QString &path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QString::fromUtf8( file.readAll() );
}

} // namespace

TEST_CASE( "lab --report exports json/md/html with runs and no secret leakage",
           "[lab_report_cli][redaction]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString db = seedStore( dir );

    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    LabReportOptions options = baseOptions( db, outDir );

    std::string error;
    const int exitCode = sicnu::cli::runLabReport( options, &error );
    INFO( error );
    REQUIRE( exitCode == exprs::exitCodeValue( exprs::ExitCode::Ok ) );

    const QString json = readFile( outDir.filePath( QStringLiteral( "report.json" ) ) );
    const QString markdown = readFile( outDir.filePath( QStringLiteral( "report.md" ) ) );
    const QString html = readFile( outDir.filePath( QStringLiteral( "report.html" ) ) );

    // The recorded truth is present...
    REQUIRE( json.contains( QLatin1String( "run-lab-1" ) ) );
    REQUIRE( json.contains( QLatin1String( "rs:spectral_index" ) ) );
    // ...and the planted secret survives in NONE of the renderings.
    REQUIRE( !json.contains( QLatin1String( "super-secret-classroom-token" ) ) );
    REQUIRE( !markdown.contains( QLatin1String( "super-secret-classroom-token" ) ) );
    REQUIRE( !html.contains( QLatin1String( "super-secret-classroom-token" ) ) );
}

TEST_CASE( "lab --report determinism: identical inputs give byte-identical files",
           "[lab_report_cli][determinism]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString db = seedStore( dir );

    QTemporaryDir out1, out2;
    REQUIRE( out1.isValid() );
    REQUIRE( out2.isValid() );

    std::string error;
    REQUIRE( sicnu::cli::runLabReport( baseOptions( db, out1 ), &error )
             == exprs::exitCodeValue( exprs::ExitCode::Ok ) );
    REQUIRE( sicnu::cli::runLabReport( baseOptions( db, out2 ), &error )
             == exprs::exitCodeValue( exprs::ExitCode::Ok ) );

    REQUIRE( readFile( out1.filePath( QStringLiteral( "report.json" ) ) )
             == readFile( out2.filePath( QStringLiteral( "report.json" ) ) ) );
    REQUIRE( readFile( out1.filePath( QStringLiteral( "report.md" ) ) )
             == readFile( out2.filePath( QStringLiteral( "report.md" ) ) ) );
}

TEST_CASE( "lab --report embeds a recorded grade from the transcript digest",
           "[lab_report_cli][grade]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString db = seedStore( dir );

    // The transcript `lab --grade --out` writes: digest is the sha256 of the
    // canonical body — here a KNOWN value the test declares itself.
    const QByteArray bodyBytes = QStringLiteral( R"({
  "lab_id": "lab-exp-1",
  "score": 95.0,
  "verdict": "pass"
})" ).toUtf8();
    const QString digest = QString::fromLatin1(
      QCryptographicHash::hash( bodyBytes, QCryptographicHash::Sha256 ).toHex() );
    QJsonObject transcript;
    transcript.insert( QStringLiteral( "schema" ), QStringLiteral( "sicnu.lab.grade/1" ) );
    transcript.insert( QStringLiteral( "digest" ), digest );
    transcript.insert( QStringLiteral( "generated_utc" ),
                       QStringLiteral( "2026-09-16T07:00:00Z" ) );
    transcript.insert( QStringLiteral( "report" ),
                       QJsonDocument::fromJson( bodyBytes ).object() );
    const QString transcriptPath = dir.filePath( QStringLiteral( "grade.json" ) );
    QFile transcriptFile( transcriptPath );
    REQUIRE( transcriptFile.open( QIODevice::WriteOnly ) );
    transcriptFile.write( QJsonDocument( transcript ).toJson( QJsonDocument::Indented ) );
    transcriptFile.close();

    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    LabReportOptions options = baseOptions( db, outDir );
    options.gradeTranscriptPath = transcriptPath;

    std::string error;
    REQUIRE( sicnu::cli::runLabReport( options, &error )
             == exprs::exitCodeValue( exprs::ExitCode::Ok ) );

    const QJsonObject document =
      QJsonDocument::fromJson( readFile( outDir.filePath( QStringLiteral( "report.json" ) ) )
                                 .toUtf8() )
        .object();
    const QJsonObject grade = document.value( QLatin1String( "grade" ) ).toObject();
    REQUIRE( grade.value( QLatin1String( "status" ) ).toString() == QLatin1String( "recorded" ) );
    // The reference is the TRANSCRIPT digest — verifiable, never orphaned.
    REQUIRE( grade.value( QLatin1String( "gradingRef" ) ).toString() == digest );
    // The embedding serializes the inline copy under "inline" (redacted).
    const QJsonObject inlineResult = grade.value( QLatin1String( "inline" ) ).toObject();
    REQUIRE( inlineResult.value( QLatin1String( "score" ) ).toDouble() == 95.0 );
    // And the transcript itself carries no secret either.
    REQUIRE( !readFile( outDir.filePath( QStringLiteral( "report.json" ) ) )
                  .contains( QLatin1String( "super-secret-classroom-token" ) ) );
}

TEST_CASE( "lab --report rejects malformed transcripts and unknown experiments",
           "[lab_report_cli][negative]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString db = seedStore( dir );

    // Wrong schema → usage failure, no files written.
    const QString badTranscript = dir.filePath( QStringLiteral( "bad.json" ) );
    {
        QFile bad( badTranscript );
        REQUIRE( bad.open( QIODevice::WriteOnly ) );
        bad.write( "{\"schema\": \"something.else/9\", \"digest\": \"x\"}" );
    }
    QTemporaryDir outDir;
    REQUIRE( outDir.isValid() );
    LabReportOptions options = baseOptions( db, outDir );
    options.gradeTranscriptPath = badTranscript;
    std::string error;
    REQUIRE( sicnu::cli::runLabReport( options, &error )
             == exprs::exitCodeValue( exprs::ExitCode::ValidationFailure ) );
    REQUIRE( error.find( "schema" ) != std::string::npos );
    REQUIRE( !QFile::exists( outDir.filePath( QStringLiteral( "report.json" ) ) ) );

    // Unknown experiment → builder's typed report_no_experiment.
    LabReportOptions unknown = baseOptions( db, outDir );
    unknown.experimentId = QStringLiteral( "no-such-lab" );
    error.clear();
    REQUIRE( sicnu::cli::runLabReport( unknown, &error )
             == exprs::exitCodeValue( exprs::ExitCode::ValidationFailure ) );
    REQUIRE( error.find( "lab.report_no_experiment" ) != std::string::npos );

    // Missing store → usage failure mentioning the store (the parent dir is
    // absent too, so the store cannot create the file).
    LabReportOptions noStore = baseOptions( dir.filePath( QStringLiteral( "missing_dir/x.db" ) ), outDir );
    error.clear();
    REQUIRE( sicnu::cli::runLabReport( noStore, &error )
             == exprs::exitCodeValue( exprs::ExitCode::ValidationFailure ) );
    REQUIRE( error.find( "experiment store" ) != std::string::npos );
}

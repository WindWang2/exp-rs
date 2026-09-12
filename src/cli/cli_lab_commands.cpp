/***************************************************************************
 * src/cli/cli_lab_commands.cpp — `lab` command: teaching auto-grader (D4)
 *
 * The grading itself lives behind the stable OutputVerifier::gradeArtifact()
 * seam (ADR 0146); this file is the CLI shell: flag parsing, transcript
 * output, --out file writing, and the exit-code contract.
 ***************************************************************************/
#include "cli_lab_commands.h"

#include "cli_commands.h"
#include "agent/output_verifier.h"
#include "exprs/exit_codes.h"
#include "lab_batch_runner.h"

#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <json/json.h>

#include <iostream>

namespace sicnu::cli {

namespace exprs_ns = exprs;

namespace {

constexpr const char *kLabUsage =
  "usage: sicnu_geo_rs_cli lab --lab <id|.rules.json> --grade <artifact>\n"
  "                            [--out report.json] [--max-bytes <n>]\n"
  "       sicnu_geo_rs_cli lab --lab <id|.rules.json> --batch <dir>\n"
  "                            [--csv grades.csv] [--max-bytes <n>]\n"
  "\n"
  "Grades a student artifact against a lab's known-answer rules\n"
  "(data/labs/grading/<id>.rules.json) and prints the JSON transcript\n"
  "({schema, digest, generated_utc, report}) on stdout.\n"
  "\n"
  "--batch streams one submission at a time over <dir> (student_id = file\n"
  "stem, sorted by name) and appends a UTF-8-BOM CSV row per submission,\n"
  "flushed incrementally; a failing submission becomes an isolated \"error\"\n"
  "row and never aborts the run. Memory stays bounded by the largest single\n"
  "artifact, not by class size (D7).\n"
  "\n"
  "Exit codes (single artifact):\n"
  "  0  pass        (score >= passing_score, no blocking failure)\n"
  "  1  fail        (graded below the pass line)\n"
  "  2  usage       (bad flags, unknown lab, invalid rules, missing artifact)\n"
  "  3  unverifiable (artifact exists but cannot be graded as a raster)\n"
  "Exit codes (--batch):\n"
  "  0  every submission graded (even if some scored \"fail\")\n"
  "  1  run completed with isolated \"error\" rows (CSV is complete)\n"
  "  2  usage (bad flags, missing submissions directory)\n";

int exitCodeFor( const sicnu::agent::OutputVerifier::LabGradeResult &result )
{
    if ( result.graded )
        return result.verdict == QLatin1String( "pass" )
                 ? exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok )
                 : exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError );
    return result.errorClass == QLatin1String( "usage" )
             ? exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure )
             : exprs_ns::exitCodeValue( exprs_ns::ExitCode::ExecutionFailure );
}

Json::StreamWriterBuilder prettyWriter()
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    builder["commentStyle"] = "None";
    return builder;
}

} // namespace

int commandLab( QStringList arguments, const CliIO &io )
{
    QString lab, artifact, outPath, batchDir, csvPath;
    bool haveMaxBytes = false;
    qint64 maxBytes = 0;

    while ( !arguments.isEmpty() )
    {
        const QString arg = arguments.takeFirst();
        if ( arg == QLatin1String( "--help" ) || arg == QLatin1String( "-h" ) )
        {
            std::cout << kLabUsage;
            return exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok );
        }
        else if ( arg == QLatin1String( "--lab" ) )
        {
            if ( arguments.isEmpty() )
            {
                std::cerr << "lab: --lab requires a value\n";
                return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
            }
            lab = arguments.takeFirst();
        }
        else if ( arg == QLatin1String( "--grade" ) )
        {
            if ( arguments.isEmpty() )
            {
                std::cerr << "lab: --grade requires a value\n";
                return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
            }
            artifact = arguments.takeFirst();
        }
        else if ( arg == QLatin1String( "--out" ) )
        {
            if ( arguments.isEmpty() )
            {
                std::cerr << "lab: --out requires a value\n";
                return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
            }
            outPath = arguments.takeFirst();
        }
        // D7 --batch branch: the only extension to D4's parser.
        else if ( arg == QLatin1String( "--batch" ) )
        {
            if ( arguments.isEmpty() )
            {
                std::cerr << "lab: --batch requires a value\n";
                return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
            }
            batchDir = arguments.takeFirst();
        }
        else if ( arg == QLatin1String( "--csv" ) )
        {
            if ( arguments.isEmpty() )
            {
                std::cerr << "lab: --csv requires a value\n";
                return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
            }
            csvPath = arguments.takeFirst();
        }
        else if ( arg == QLatin1String( "--max-bytes" ) )
        {
            if ( arguments.isEmpty() )
            {
                std::cerr << "lab: --max-bytes requires a value\n";
                return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
            }
            bool ok = false;
            maxBytes = arguments.takeFirst().toLongLong( &ok );
            if ( !ok || maxBytes <= 0 )
            {
                std::cerr << "lab: --max-bytes must be a positive integer\n";
                return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
            }
            haveMaxBytes = true;
        }
        else
        {
            std::cerr << "lab: unknown option \"" << arg.toStdString() << "\"\n" << kLabUsage;
            return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
        }
    }

    if ( lab.isEmpty() || ( artifact.isEmpty() && batchDir.isEmpty() ) )
    {
        std::cerr << "lab: --lab and (--grade or --batch) are required\n" << kLabUsage;
        return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
    }

    sicnu::agent::OutputVerifier::LabGradeOptions options;
    if ( haveMaxBytes )
        options.maxBytes = static_cast<std::size_t>( maxBytes );

    const sicnu::agent::OutputVerifier verifier;

    // D7 --batch: streamed, error-isolated CSV grading over a directory.
    if ( !batchDir.isEmpty() )
    {
        const QString csv =
          csvPath.isEmpty() ? QStringLiteral( "grades.csv" ) : csvPath;
        const auto summary =
          LabBatchRunner::run( batchDir, lab, csv,
                               [&verifier, &lab, &options]( const QString & artifactPath )
                               { return verifier.gradeArtifact( lab, artifactPath, options ); } );

        if ( summary.usageError )
            std::cerr << "lab: cannot read submissions directory: "
                      << batchDir.toStdString() << "\n";
        else
            std::cerr << "batch: graded " << summary.graded << "/" << summary.total
                      << " submissions";
        if ( summary.isolated > 0 )
            std::cerr << " (" << summary.isolated
                      << " isolated as \"error\" rows — see the CSV)";
        if ( !summary.usageError )
            std::cerr << "\n";

        Json::Value envelopeData;
        Json::Value &batch = envelopeData["batch"];
        batch["submissions_dir"] = batchDir.toStdString();
        batch["csv"] = csv.toStdString();
        batch["total"] = summary.total;
        batch["graded"] = summary.graded;
        batch["isolated"] = summary.isolated;

        const int exitCode = batchExitCodeFor( summary );
        io.finish( exitCode == exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok ),
                   "lab", envelopeData, exitCode, {},
                   summary.usageError
                     ? "cannot read submissions directory: " + batchDir.toStdString()
                     : std::string() );
        return exitCode;
    }

    const auto result = verifier.gradeArtifact( lab, artifact, options );

    // Timestamp lives in the header only and is excluded from the digest
    // (autonomy default 6: byte-identical reports for identical inputs).
    const QString generatedUtc =
      QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs );
    const Json::Value document = result.toJson( generatedUtc );

    if ( io.json )
    {
        Json::Value envelopeData;
        envelopeData["lab"] = document;
        io.finish( result.graded && result.verdict == QLatin1String( "pass" ), "lab",
                   envelopeData, exitCodeFor( result ), {},
                   result.graded ? std::string() : result.error.toStdString() );
    }
    else
    {
        std::cout << Json::writeString( prettyWriter(), document ) << "\n";
    }

    if ( !outPath.isEmpty() )
    {
        QFile out( outPath );
        if ( !out.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        {
            std::cerr << "lab: cannot write report file: " << outPath.toStdString() << "\n";
            return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
        }
        out.write( Json::writeString( prettyWriter(), document ).c_str() );
        out.write( "\n" );
    }

    if ( !result.graded )
        std::cerr << "lab: " << result.error.toStdString() << "\n";

    return exitCodeFor( result );
}

} // namespace sicnu::cli

/***************************************************************************
 * src/cli/lab_report_runner.cpp — headless lab report export (see header)
 ***************************************************************************/
#include "lab_report_runner.h"

#include "experiment/bridge/lab_report.h"
#include "experiment/bridge/lab_report_writers.h"
#include "experiment/experiment_store.h"
#include "exprs/exit_codes.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>

#include <string>

namespace sicnu::cli {

namespace exprs_ns = exprs;

namespace {

constexpr const char *kGradeSchema = "sicnu.lab.grade/1";

/// Loads a grade transcript produced by `lab --grade --out` and builds the
/// RECORDED grade embedding: gradingRef is the transcript digest, so the
/// inline copy is verifiable against the original document, never orphaned.
bool recordedGradeFromTranscript( const QString &path, sicnu::experiment::LabGradeEmbedding *grade,
                                  QString *error )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        *error = QStringLiteral( "cannot read grade transcript: %1" ).arg( path );
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
    {
        *error = QStringLiteral( "grade transcript is not valid JSON: %1 (%2)" )
                   .arg( path, parseError.errorString() );
        return false;
    }
    const QJsonObject root = doc.object();
    if ( root.value( QLatin1String( "schema" ) ).toString()
         != QLatin1String( kGradeSchema ) )
    {
        *error = QStringLiteral( "%1: schema must be %2" ).arg( path, kGradeSchema );
        return false;
    }
    const QString digest = root.value( QLatin1String( "digest" ) ).toString();
    if ( digest.isEmpty() )
    {
        *error = QStringLiteral( "%1: transcript carries no digest" ).arg( path );
        return false;
    }
    const QJsonObject body =
      root.value( QLatin1String( "report" ) ).toObject();

    grade->status = QStringLiteral( "recorded" );
    grade->gradingRef = digest;
    QJsonObject details;
    details.insert( QStringLiteral( "source" ), QFileInfo( path ).absoluteFilePath() );
    details.insert( QStringLiteral( "lab_id" ),
                    root.value( QLatin1String( "report" ) ).toObject()
                      .value( QLatin1String( "lab_id" ) ).toString() );
    details.insert( QStringLiteral( "generated_utc" ),
                    root.value( QLatin1String( "generated_utc" ) ).toString() );
    grade->gradingRefDetails = details;
    grade->inlineResult = body;
    return true;
}

} // namespace

int runLabReport( const LabReportOptions &options, std::string *errorOut )
{
    namespace exprs_ns = exprs;
    const auto fail = [errorOut]( const QString &message ) -> int
    {
        *errorOut = message.toStdString();
        return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
    };

    if ( options.experimentDb.isEmpty() || options.experimentId.isEmpty()
         || options.outBase.isEmpty() )
        return fail( QStringLiteral(
          "lab --report requires --experiment-db, --experiment and --report-out <base>" ) );

    sicnu::experiment::ExperimentStore store;
    QString storeError;
    if ( !store.open( options.experimentDb, &storeError ) )
        return fail( QStringLiteral( "cannot open experiment store: %1" ).arg( storeError ) );

    sicnu::experiment::LabReportRequest request;
    request.labId = options.experimentId;
    request.labName = options.labName;
    request.objective = options.objective;
    request.student = options.student;
    request.session = options.session;
    request.runId = options.runId;
    request.generatedAtUtc = options.generatedAtUtc;

    if ( !options.gradeTranscriptPath.isEmpty() )
    {
        sicnu::experiment::LabGradeEmbedding grade;
        QString gradeError;
        if ( !recordedGradeFromTranscript( options.gradeTranscriptPath, &grade, &gradeError ) )
            return fail( gradeError );
        request.grade = grade;
    }

    sicnu::experiment::LabReportBuilder builder( store, nullptr );
    const auto built = builder.build( request );
    if ( !built )
    {
        QString message;
        for ( const auto &diagnostic : built.diagnostics() )
        {
            if ( !message.isEmpty() )
                message += QStringLiteral( "; " );
            message += diagnostic.code + QStringLiteral( ": " ) + diagnostic.message;
        }
        return fail( message.isEmpty() ? QStringLiteral( "lab report build failed" ) : message );
    }

    const auto written =
      sicnu::experiment::writeLabReportFiles( built.value(), options.outBase );
    if ( !written )
    {
        QString message;
        for ( const auto &diagnostic : written.diagnostics() )
        {
            if ( !message.isEmpty() )
                message += QStringLiteral( "; " );
            message += diagnostic.code + QStringLiteral( ": " ) + diagnostic.message;
        }
        return fail( message.isEmpty() ? QStringLiteral( "lab report write failed" ) : message );
    }
    return exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok );
}

} // namespace sicnu::cli
